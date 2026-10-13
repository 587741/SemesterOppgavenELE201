# ELE201 Semester Project – STM32 Chronometer with Ethernet Communication

This repository contains an ELE201 semester project built around an STM32F767ZI chronometer with Ethernet communication.

The chronometer is controlled by a push button, uses TIM2 to measure elapsed time, an external LED to indicate the running state, and USART3 for serial output. The STM32 sends the current chronometer state and elapsed time to a Mongoose web server using HTTP and JSON. A web page retrieves and displays the latest values.

## Repository Structure

```text
SemesterOppgaven/
├── README.md
├── stm32/
├── server/
├── packet-tracer/
└── report/
```

The `stm32/` folder contains the STM32CubeMX and PlatformIO project. The `server/` folder contains the Mongoose server and web interface. The `packet-tracer/` folder contains the Cisco Packet Tracer network project. The `report/` folder contains the final semester project report.

## System Flow

```text
Push Button
    |
    v
STM32 Chronometer
    |
    | HTTP POST + JSON
    v
Mongoose Server
    |
    | HTTP GET
    v
Web Browser
```

Example JSON sent by the STM32:

```json
{
  "state": "RUNNING",
  "elapsed_ms": 12540
}
```

The STM32 sends updates when the chronometer starts or stops and also sends periodic updates while the system is running.

## Packet Tracer Network

The simulated Cisco Packet Tracer network uses the address space `192.168.0.128/25`.

The network is divided using VLSM as follows:

| Purpose | Subnet |
|---|---|
| Sensor / MCU network | `192.168.0.128/26` |
| Server network | `192.168.0.192/29` |
| Management network | `192.168.0.200/29` |
| Router-to-router link | `192.168.0.208/30` |

Static routing is configured between the routers. Connectivity was verified between the microcontroller network, server network, and management network.

The Packet Tracer network is a simulation and is separate from the physical LAN used during STM32 Ethernet testing. The physical network may therefore use a different IP address range.

## Build and Run

To build the STM32 firmware, open the `stm32/` folder in Visual Studio Code with PlatformIO and use:

```text
PlatformIO: Build
PlatformIO: Upload
```

The serial monitor runs at:

```text
115200 baud
```

To build the Mongoose server on Windows with GCC:

```bash
gcc main.c mongoose.c -o server.exe -lws2_32
```

Run the server with:

```powershell
.\server.exe
```

The web interface is served on port `8000`.

## Technologies Used

STM32F767ZI, STM32 HAL, TIM2, GPIO interrupts, USART3, RMII Ethernet, LAN8742 PHY, LwIP, DHCP, TCP, HTTP, JSON, Mongoose, Cisco Packet Tracer, PlatformIO and STM32CubeMX.

## Report

The final project report is included in the `report/` folder.