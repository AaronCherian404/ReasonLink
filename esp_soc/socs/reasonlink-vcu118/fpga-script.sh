# The lab VCU118's connection settings. Source once per terminal before any
# make target that touches the board (fpga-program, fpga-run, fpga-run-linux, uart):
#   cd ~/esp/socs/reasonlink-vcu118 && source fpga-script.sh
# Same values as the course-provided fpga-script.sh. The board's own Ethernet
# address (IP C0A80111, MAC A6A7A0F80444) is in esp_defconfig.
export FPGA_HOST=espdev
export XIL_HW_SERVER_PORT=3132
export UART_IP=espdev
export UART_PORT=4331
export SSH_IP=espgate
export SSH_PORT=5508
export ESPLINK_IP=128.59.22.75
export ESPLINK_PORT=46314
export LINUX_MAC=000A350746D0
