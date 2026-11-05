# Uploaded images and recovery

The five user-provided images are organized in [../images/](../images/). The SWD photograph and three pinout PNGs were moved without changing their bytes. These are user-provided references; the PNGs are not newly authored datasheets. Verify against the exact device/package before probing.

The root `PCBimage_without_shielding.jpeg` contained only two newline bytes, rather than a photograph. Those original bytes are retained as [original-pcb-placeholder.jpeg](original-pcb-placeholder.jpeg). The one-byte file formerly named `docs/images` is retained as [original-images-placeholder.txt](original-images-placeholder.txt). Neither placeholder is a usable image.

The actual component-side photograph was recovered from the full-resolution 4032×3024 color JPEG embedded in `IMG_1151.dng`, retained in [Git history at 3e7e7b8](https://github.com/allarasp/DX-LR32_custom_firmware_for_Meshtastic/blob/3e7e7b87322399532c083da0bd8453f54779864c/IMG_1151.dng). Camera/GPS metadata was omitted from the new JPEG; orientation 6 was retained. Its compressed pixel data was not recompressed. The original DNG remains in Git history; it is not duplicated here.

Shield removal was for research, **not required for flashing through the back SWD pads**.
