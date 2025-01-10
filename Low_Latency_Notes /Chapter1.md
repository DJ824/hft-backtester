## Chapter 1: Introdocuing Low Latency 

Latency: time delay between when a task is started to the time when the task is finished 
- low latency applications are applications that exeute tasks and respond or return results as quickly as possible 
- latency sensitive application: as performance latencies are reduced, it imporves the business impact or profitablity 
- system might still be functional, but will be significantly more profitable if latencie are reduced, (OS, web browsers, databases) 
- latency criticla application: one that fails completely if performance latency is highe rthan a certain threshold 
- examples include air traffic control systems, financial trading systems, autonomous vehicles, etc 

### Measuring Latency 

- there are different methods to measuring latency, real difference from these methods comes down to what is considred the beginning
of the processing task, and what is the end of the processing task 
- another approach would be the units of what we are measuring, time is common, but CPU clock cycles can also be used if it comes down to instruction-level measurements
- in a given client server model: 
- t1: time server puts firt byte of packet on the wire 
- t2: time client reads first byte of packet from wite 
- t3: time client puts a packet containing a response on the wire 
- t4: time server reads first byte of packet containing response from wire 
- Time to first byte: measure as the time elapsed from when the sender sends the first byte of a request or response
to the momeny when the reciever recieves the first byte (difference btwen t4 and t3) 
- Round Trip Time: sum fo time is takes for a packet to travel from one process to antoehr, and then the time it takes for the
response packet to reach the original process, in trading based on 3 components: 
1) Time it takes for information from exchange to reach the participant 
2) Time it takes for the execution of algorithms to anaylze the information and make a decision 
3) Time if takes for the decision to each the exchange and get processed by the matching engine 
- Tick to Trade: time from when a pakcet (market data packett) first hits a participating infrastructure (trading server)
to the time when the participant is done processing the packer and sends  apcket out(order request) to the trading echange 
- in other words, time spend by insfrastrucutre to read packet, process it, calculate trading signals, generate an order request
in response fo that, and put it on wire(write to a socket) (difference between t3 and t2) 
- CPU clock cycles: smallest increment of work that cna be done by the CPU processor, in reality, theyare the amt of time btween 2 pulses of the 
oscillator that drives the CPU processor 


### Differentiating between latency metrics 

- relative performance of a specific maltency metric over the other depends on the application and the business itself, fo rex) a latency ciritcla application such as AV cares about peak latency more than mean latency 
- trading systems care more about mean latency and smaller latency variance than about peak latency 

#### Throughput vs Latency 

Throughput: defined as how much work gets done in a certain period of time
Latency: how quickly a single task is completer 
- to improve throughput, usual method is to introduce parallelism and add additional computing, memory, and netowrking resources, note that each indivicual task might not be completed as 
quickly as possible, but overall, more tasks will be completed after a certain amt of time 

#### Mean latency 
- average response time of a system, simply the average of all the latency measuremet observations 

#### Median latency
- typcially a better metric for the expected response time of a system, since it is the median of the latenyc measurement observations, it exclused the impact of large outliers

#### Peak Latency 
- important metric for systems where a single alrge outlier in performance can have a devasting impact on the system, 

### Requirements of latency-sensitive applications

1) Correctness and robustness
- we would think that low latency is the most important part of these applications, but in reality, it is stringent robustness and fualt tolernace 
2) Low latencies on average
- the expected reaction or processing latency needs to be as low as possible for theapplication or business to succeed
3) Capped peak latency 
- there should be a well defined upper threshold on the max latency
4) Predicatble latency - low latency variance
- some applications prefer tha tthe expected perofmranc elatency is predicatble, even if that means sacricing latency a little bit if average latency metric is higher than it could be 

