// SPDX-License-Identifier: GPL-2.0
/*
 * Linux driver for the SAT-Accel ESP third-party tile (CU_SATACCEL).
 *
 * This is the kernel half of what XRT does for SAT-Accel's host.cpp; the
 * register programming itself (setArg, start, wait, read-back) runs in user
 * space in the shared driver core (sataccel_hw/drv/sataccel_drv.c) over the
 * mappings this driver provides:
 *
 *   mmap offset 0                   the tile's 1 MB register window (uncached)
 *   mmap offset SATACCEL_MEM_PGOFF  a physically contiguous DMA buffer from
 *                                   ESP's reserved third-party memory
 *                                   (device tree memory-region)
 *   ioctl SATACCEL_IOC_INFO         buffer physical address and sizes
 *   ioctl SATACCEL_IOC_WAIT         sleep until the completion interrupt
 *
 * At probe it puts the tile in coherent-DMA mode (ACC_COH_RECALL) through the
 * tile CSRs, as ESP's nv_nvdla_coh example does for NVDLA, so the CPU needs
 * no cache flushes around a run.
 */
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include "sataccel_ioctl.h"

#define DRV_NAME "sataccel"

/* ESP tile CSRs (soft/common/apps/baremetal/nv_nvdla_coh) */
#define ESP_CSR_BASE        0x60090000UL
#define ESP_CSR_TILE_SIZE   0x200
#define ESP_COH_REG_INDEX   99
#define ESP_ACC_COH_RECALL  2

/* SAT-Accel registers used by the interrupt handler (sataccel_drv.h) */
#define SA_SOLVER_ISR       (0x60000 + 0x0c)

static unsigned int tile = 4;
module_param(tile, uint, 0444);
MODULE_PARM_DESC(tile, "ESP tile number of the SAT-Accel tile (for its coherence CSR)");

static unsigned long mem_size = SATACCEL_DEFAULT_MEM_SIZE;
module_param(mem_size, ulong, 0444);
MODULE_PARM_DESC(mem_size, "Bytes of DMA memory to reserve for SAT-Accel's buffers");

struct sataccel {
	struct device *dev;
	void __iomem *regs;
	resource_size_t regs_phys, regs_size;
	void *mem_virt;
	dma_addr_t mem_dma;
	size_t mem_size;
	int irq;
	struct completion done;
	struct mutex lock;
	struct miscdevice misc;
};

static irqreturn_t sataccel_isr(int irq, void *data)
{
	struct sataccel *sa = data;

	/* Clear the solver's ap_done status (toggle on write) so the level
	 * interrupt drops; user space still sees ap_done in AP_CTRL. */
	iowrite32(1, sa->regs + SA_SOLVER_ISR);
	complete(&sa->done);
	return IRQ_HANDLED;
}

static int sataccel_open(struct inode *inode, struct file *file)
{
	struct sataccel *sa = container_of(file->private_data, struct sataccel, misc);

	file->private_data = sa;
	return 0;
}

static long sataccel_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sataccel *sa = file->private_data;

	switch (cmd) {
	case SATACCEL_IOC_INFO: {
		struct sataccel_info info = {
			.mem_phys = sa->mem_dma,
			.mem_size = sa->mem_size,
			.regs_size = sa->regs_size,
		};
		return copy_to_user((void __user *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case SATACCEL_IOC_ARM:
		reinit_completion(&sa->done);
		return 0;
	case SATACCEL_IOC_WAIT: {
		struct sataccel_wait w;
		long left;

		if (copy_from_user(&w, (void __user *)arg, sizeof(w)))
			return -EFAULT;
		left = wait_for_completion_interruptible_timeout(&sa->done,
								 msecs_to_jiffies(w.timeout_ms));
		if (left < 0)
			return left;
		return left ? 0 : -ETIMEDOUT;
	}
	default:
		return -ENOTTY;
	}
}

static int sataccel_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sataccel *sa = file->private_data;
	unsigned long len = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff == 0) {
		if (len > sa->regs_size)
			return -EINVAL;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		return io_remap_pfn_range(vma, vma->vm_start, sa->regs_phys >> PAGE_SHIFT, len,
					  vma->vm_page_prot);
	}
	if (vma->vm_pgoff == SATACCEL_MEM_PGOFF) {
		if (len > sa->mem_size)
			return -EINVAL;
		vma->vm_pgoff = 0;
		return dma_mmap_coherent(sa->dev, vma, sa->mem_virt, sa->mem_dma, len);
	}
	return -EINVAL;
}

static const struct file_operations sataccel_fops = {
	.owner = THIS_MODULE,
	.open = sataccel_open,
	.unlocked_ioctl = sataccel_ioctl,
	.mmap = sataccel_mmap,
};

static int sataccel_set_coherence(struct device *dev)
{
	void __iomem *csr = ioremap(ESP_CSR_BASE + tile * ESP_CSR_TILE_SIZE, ESP_CSR_TILE_SIZE);

	if (!csr)
		return -ENOMEM;
	iowrite32(ESP_ACC_COH_RECALL, csr + 4 * ESP_COH_REG_INDEX);
	dev_info(dev, "tile %u: coherence mode %u (coherent DMA)\n", tile,
		 ioread32(csr + 4 * ESP_COH_REG_INDEX));
	iounmap(csr);
	return 0;
}

static int sataccel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sataccel *sa;
	struct resource *res;
	u32 id;
	int ret;

	sa = devm_kzalloc(dev, sizeof(*sa), GFP_KERNEL);
	if (!sa)
		return -ENOMEM;
	sa->dev = dev;
	init_completion(&sa->done);
	mutex_init(&sa->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	sa->regs_phys = res->start;
	sa->regs_size = resource_size(res);
	sa->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(sa->regs))
		return PTR_ERR(sa->regs);

	id = ioread32(sa->regs + SATACCEL_SHELL_ID_OFF);
	if (id != SATACCEL_SHELL_ID) {
		dev_err(dev, "unexpected shell ID 0x%08x\n", id);
		return -ENODEV;
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		dev_warn(dev, "no reserved memory region (%d); using the default DMA pool\n", ret);
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_mem;
	sa->mem_size = PAGE_ALIGN(mem_size);
	sa->mem_virt = dma_alloc_coherent(dev, sa->mem_size, &sa->mem_dma, GFP_KERNEL);
	if (!sa->mem_virt) {
		dev_err(dev, "cannot allocate %zu bytes of DMA memory\n", sa->mem_size);
		ret = -ENOMEM;
		goto err_mem;
	}

	ret = sataccel_set_coherence(dev);
	if (ret)
		goto err_dma;

	sa->irq = platform_get_irq(pdev, 0);
	if (sa->irq < 0) {
		ret = sa->irq;
		goto err_dma;
	}
	ret = devm_request_irq(dev, sa->irq, sataccel_isr, 0, DRV_NAME, sa);
	if (ret)
		goto err_dma;

	sa->misc.minor = MISC_DYNAMIC_MINOR;
	sa->misc.name = DRV_NAME;
	sa->misc.fops = &sataccel_fops;
	sa->misc.parent = dev;
	ret = misc_register(&sa->misc);
	if (ret)
		goto err_dma;

	platform_set_drvdata(pdev, sa);
	dev_info(dev, "SAT-Accel at %pa (irq %d), %zu MB DMA buffer at 0x%llx\n", &sa->regs_phys,
		 sa->irq, sa->mem_size >> 20, (unsigned long long)sa->mem_dma);
	return 0;

err_dma:
	dma_free_coherent(dev, sa->mem_size, sa->mem_virt, sa->mem_dma);
err_mem:
	of_reserved_mem_device_release(dev);
	return ret;
}

static int sataccel_remove(struct platform_device *pdev)
{
	struct sataccel *sa = platform_get_drvdata(pdev);

	misc_deregister(&sa->misc);
	dma_free_coherent(sa->dev, sa->mem_size, sa->mem_virt, sa->mem_dma);
	of_reserved_mem_device_release(sa->dev);
	return 0;
}

static const struct of_device_id sataccel_of_match[] = {
	{ .compatible = "columbia,sataccel" },
	{ }
};
MODULE_DEVICE_TABLE(of, sataccel_of_match);

static struct platform_driver sataccel_driver = {
	.probe = sataccel_probe,
	.remove = sataccel_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sataccel_of_match,
	},
};
module_platform_driver(sataccel_driver);

MODULE_DESCRIPTION("SAT-Accel ESP third-party tile driver (ReasonLink)");
MODULE_LICENSE("GPL");
