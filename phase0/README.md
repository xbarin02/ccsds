# Open122

Open122 is an open-source CCSDS 122.0 codec written in C programming language.
The CCSDS 122.0 is a standard for lossy-to-lossless image data compression.
The compression method targets instruments used on board of spacecraft.

### Current status

- The code fully implements the CCSDS 122.0 standard up to Stage 1.
- Further stages have to be implemented.

### Prerequisites

The library is written in pure C89 (ANSI C). No compiler extensions nor
assembly language are employed. No third-party libraries are needed. It does
not even rely on POSIX. You only need working compiler toolchain.

### Installing

To build the library, run the following command in source tree directory:

```
make
```

## Authors

* David Barina <ibarina@fit.vutbr.cz>

## License

This project is licensed under the MIT License. See the [LICENSE.md](LICENSE.md)
file for details.

## Acknowledgments

This work has been supported by the ECSEL-JU European research project AQUAS.
