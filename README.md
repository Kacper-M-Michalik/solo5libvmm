This library provide helper functions for all setup and most processing required to run a solo5 based HVT unikernel in Microkit.
You must implement a Microkit PD using the functions provided, and are resposible for the implemenation of the hypercall handlers.
Example systems/PDs built using this library can be found here: https://github.com/Kacper-M-Michalik/Mirage-Sel4-Templates

This library depends on solo5 and libmicrokit, and can be used in two ways:
You can add the include/ and src/ folders into your project and write your own build system.
You can run 'make ...' to create a library, and simply link against it.