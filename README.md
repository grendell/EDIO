# EDIO
* For use with the [Everdrive N8 Pro](https://krikzz.com/our-products/cartridges/everdrive-n8-pro-72pin.html)
* Intended to facilitate transferring in-development ROM files from MacOS or Linux.
* Based on [edlink-n8](https://github.com/krikzz/EDN8-PRO/tree/master/edlink-n8) by [krikzz](https://twitter.com/krikzz)
## Building
* Simply run `make`
## Usage
* `./edio <local/path/to/src.nes> <remote/path/to/dst.nes> [/dev/path_to_everdrive]`
* Everdrive path defaults to `/dev/cu.usbmodem00000000001A1`