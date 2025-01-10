## Designing Some Common Low Lantency Applications 

- understanding low latency performance in video streaming applications 
- understand which low latency contrains matter in gaming applciations 
- discussing the design of IOT (internet of things) and retail analylitcs systems 
- exlporing low latency trading 

### Understand low latency peroformance in live video streaming applcaitions 

Latency in video streaming: audio video content delivered in real time, or near real time 
- in this case, latency refers to the time from when a live video stream hits the camera on a recording device, and then gets transported 
to the audiences video streams, where it gets rendered (glass to glass latency) 

Video distribution service(VDS): system responsible for taking multiple incoming streams of video and audio from the sources and presenting them to the viewers (CDN) 
1) Transcoding: process of decoding a media stream from one format and recoding it to another 
2) Transmuxing: similar to transcoding but ehre, the delivery format changes without any changes to the encoding, as in the case of transcoding 
3) Transrating: changing the video bitra,te compressed to a lower value 

Discussing steps in glass to glass journey
- 2 forms of latency, initial startup latency and then lag btwen video frmaes once the live stream starts 
1) camera capturing and processing audio/video at the broadcaster 
2) video consumption and packaging at the broadcaster 
3) encoders transcoding, transmuxing, and transrating the content 
4) Sending the data over network over the appropriate protocols 
5) Distribution ofer a VDS such as a CDN 
6) reception are the recievers and buffereing 
7) decoding the content on the viewers device 
8) dealing with packet drops, network changes, and so on at the receiver end 
9) rendering the av content on viewers device 
10) possible collecting interactive inputs from viewer and sending back to broadcaster 

Possibilities of high latencies on the path 
1) Phsycial distance, server load, internet quality
2) Capture equipment and hardware, taking an AV frame and turning it into digial signals takes time, 
3) Streaming protocal, transmission, and jitter buffer
4) Encoding, transcoding and transrating 
5) Decoding and played on viewers device 

Measuring latencies in low latency video streaming 
- clapperboard - tool used to synchronize video and audoi during filmmaking and apps are availble to detect synchonizatio issues btwn 
2 streams due to latency 

### Understanding the need for low latency in modern electronic trading 

- with the moderization of electronic trading and the rise of HFT, low latencies are more important than ever before for these applications
- trading opportonuties in modern electronic markets are extremely short lived, so only the market participants that can process data
and find an opportunity to quiclkly send orders in reaction to this can be profitable 
- market making algo: har orders in the market that other participants can trade against when needed, thus needs to constantly reevalaute its 
active orders and change the prices and quantities for them depending on the market conditions 
- makret taking algo: does not always hae active orders in the market, waits for an opportunity to present itself then trades against a market-making algorithms 
active order in the book
- market making algos lose money when it is slow at modifying its active orders in the market 
for ex) lets say mkt is signaling a long, the limit sells do not want to sell at that price anymore, thus they have to quickly modify/cancel their orders, however 
a liquidity taking algo would market into those limit sells, filling them
- if the mm algo is slow, they will get filled at a price they dont want to, and if the mt algo is slow, someone else will execute at the best price available
- for hft, trading applications on the clients side can receive and process market data, analyze the information, look for opportunites, adn send an order out to the exchange 
all within sub 10 microsecond latency, using FPGA(field programmable gate arrays) can reduce this to sub 1 microsecond latency 
- firms employ hft strats across many different instruments/exchanges, thus the real time risk system must be able to constantly recaculate based on real time data 

#### Achieveving the lowest latencies in electronic trading 

1) Optimizing server hardware 
- typically, they have high CPU usage, low kernel usage (system calls), low memory consumption, and relatively high network resource usage during mkt hours 
- CPU registers, cache architecture, and capacity matter al well, and we want this to be as large as possible 
- advanced considerations such as Non-Uniform Memory Access (NUMA), processor instruction sets, instruction pipelines and paralleism, cache hierarchy details, overclocking etc are often considered as well 

2) Network Interface cards, switches, kernel bypass 
- trading servers that support ultra low latency need specialized network interface cards (NICs) and switches, these cards need to have low latency performance, low jitter, and large buffer capacities to hanlde market data burts without dropping packets 
- optimal NIC's for modern electronic trading applications support an especially low-latency path taht avoids system calls and buffer copies -> kernel bypass 
- network switches also show up in various places in the network topology, which support interconnectivity between trading servers and electronic exchange servers 
- for network switches, one fo the most important considerations is the size of the buffer that the switch can support ot buffer packets that need to be forwarded 
- antoerh important requirment is the the latency btwn a switch recieving a packet and forwarding it to the correct interface knowsn at switching latency

3) Unterstanding multithreading, locks, context switches, and CPU scheduling
- it is sometimes incorrectly assumed that having an arhchitecture with larger number of threads is always lower latency, but this is not aalways true 
- multithreading adds value in certain areas of low latency electronic trading systems, but we need to be careful which procecces we allocate to seperate threads, 
as it can sometimes end up increasing latencies in applications as well
- as we increase the number of threads, we must think about concurrency and thread safety, and if we need to use locks for synchronization and concurrency btwn threads, 
that adds additional latencies and context switches 
- context switches are not free because the scheduler and OS must save the state of the thread of process being switched out and load the state of the thread of process that will be run next
- many lock implementatiaons are built on top of kernel system calls, which are more expensive than user space routines, thus increasing the latencies 
in a heavily multithreaded application even further 
- for optimal performance, we try to get the CPU scheduler to do little to no work, (processes and threads that are schduled to run are never context swithched out and keep running in user space) 
- also common to pin specific threads to cpu cores, which elimate s context switching and the OS needing to find free cores to schedule tasks 

4) Dynamic memory allocations 
- request for memory blocks or arbitrary sizes made at runtime, handled by OS looking through a list of free mem blocks and trying to allocate a contiguous block of memory depending on the size needed 
- dellocations are handled by appending the freed blocks to the list of free blocks managed by the OS, searching throuhg this list cna incur higher and higher latencies as the program runs and memory gets increasingly fragmented 
- if allocations and dellocations are on the same critical path (longest sequence of operations that data must pass through durig a computation or instruction execution in CPU pipeline, the critical path in a processor design impacts the clock speed because it defines the minimum time required 
for a signal to propogate through the circuit), they incur an additional overhead everytime 

5) Static vs dynamic linking and compile time vs runtime 
- linking is the compilation or translation step in the process of converint high level programming language source code into machine code for target architecture, ties toget pieces of code that might be in different libraries 
- 2 types of linkign, static linking and dynimc linking, 
- dynamic linking: when the linker does not incorporate the code from libraries into the final binary at linking time, when the main application requires code from the shared libraries, 
for the first time, the resolution is performned at runtime, bigger initial cost at runtime the first time the library is called 
- downside of dynamic lnking is that since hte compiler and linker do not invorpoarte the code at compilationa dn linking time, they are unable to perform possible optimizations, resulting in an application that cna be ineffecient overall 
- static linking: when the linker arranges the application code and the code for the library dependencies into a single binary executale file, upside here is taht libraries are arelady linked at compile time, 
so there is no need for the OS to find and resolve the dependencies befoat runtime, bigger upside is taht this creates an opportunity for for the program to be super optimized at compile and linking time, to yield lower latencies at runtime 
- downside is the applicationa binary is larger and each binary that relies on the same set of external libs has a copy of all the code, low latency mainly uses static linking to minimize runtime performance latencies 

 




