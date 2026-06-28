import usb.core
import libusb_package
import can

dev = usb.core.find(idVendor=0x1D50, idProduct=0x606F,
                    backend=libusb_package.get_libusb1_backend())

bus = can.Bus(interface="gs_usb", channel=dev.product,
              bus=dev.bus, address=dev.address, bitrate=500000)

cap = bus.gs_usb.device_capability
print("Adapter CAN clock (fclk_can) =", cap.fclk_can, "Hz")

for sp in (75.0, 87.5):
    t = can.BitTiming.from_sample_point(f_clock=cap.fclk_can,
                                        bitrate=500000, sample_point=sp)
    print(f"  sample point {sp:>5}%  ->  {t}")

bus.shutdown()