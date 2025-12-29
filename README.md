# LanDrop
Zero-config LAN file transfer with discovery, resume, and integrity verification.

# Windows build (VCPKG + MSVC)
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release

# UNIX BASED
cmake -S . -B build
cmake --build build
