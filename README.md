# Solo5libvmm
This library performs most heavy-lifting for Microkit PDs (acting as VMM's) to run solo5 HVT programs inside virtual machines.
<br>
Example systems and PDs built using this library, alognside HVT ABI documentation can be found here: https://github.com/Kacper-M-Michalik/mirage-sel4-templates

### Dependencies
This library depends on libmicrokit, and the user providing ```printf``` and ```_assert_fail``` implementations.
<br>

### Building
- You can add the include/ and src/ folders into your project and write your own build system.
- You can ```include solo5libvmm.mk```, which will result in a solo5libvmm.a library being built for linking.

### What the library provides
This library provides functionality to verify and load guest images, pause/resume guests, and deal with fault decoding. 
<br>
The library does not itself implement handling of hypercalls, this is up to you and your system to implement; for example if you decode a valid hypercall, your VMM component can make a protected call or notify another component such as a device driver component to fulfill the requested hypercall; alternatively, you could make a complex 'master' component that acts as a VMM and implements device drivers/hypercalls services internally, this quickly runs into issues of hardware multiplexing should you desire to run multiple guests in parallel.
