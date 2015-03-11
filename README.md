# beagleusb
Beagleboard USB driver for Mouse, Keyboard, Audio playback


Integration of https://github.com/azizulhakim/beagledroid-usbaudio & https://github.com/azizulhakim/beagledroid-kbd together


How To Test:

1. Modify line 271 in "beagleusb.c" with your device's info. Use "lsusb -v" command to find out product id, vendor id, bIntefaceclass, bInterfaceSubclass, bProtocol

	After you get those info, you can just replace line 271 with following line format(including "comma"):

	{ USB_DEVICE_AND_INTERFACE_INFO(product_id, vendor_id, bInterfaceClass, bInterfaceSubclass, bProtocol) },



2. Make and install the kernel module. Command to install "sudo insmod beagle.ko"

3. Download android app source from https://github.com/azizulhakim/BeagleRemoteDisplay. Build the source and install apk in android device. You can also find an APK in this repository under "Test" folder.

4. Connect your android device. Run the app and click on "Connect" button

5. Use the image area as touch pad area. You can also toggle keyboard for writing.

6. From Ubuntu dashboard, open "sound settings". You'll see the Android device is listed as a sound card. Select it and play anything on your computer.


So difficult???? Just look at this youtube link. https://www.youtube.com/watch?v=06ddznhLpqc
