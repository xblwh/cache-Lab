# cache实验



## 一、软件模拟cache

通过软件来模拟硬件cache（C实现）。



#### 1. 测试目标

测试cache模拟器所需要用到的目标文件是由工具valgrind生成trace文件，例如执行命令`> valgrind --log-fd=1 --tool=lackey -v --trace-men=true ls -l`	就能依次获得linux在执行`ls -l`命令过程中对内存进行的一系列操作。摘取其中关于内存的访问记录，生成的记录格式如下：

``` shell
I 0400d7d4,8
 M 0421c7f0,4
 L 04f6b868,8
 S 7ff0005c8,8
```

每一行指令的格式为`[space]operation address,size`

其中I表示载入指令，L表示载入数据，S表示存储数据，M表示修改数据。我们实现的cache模拟器只考虑数据有关的操作，将忽略I开头的指令。

*valgrind可以用来检查内存泄漏问题。<http://valgrind.org/docs/manual/quick-start.html#quick-start.intro>*

​	

​	

#### 2. 模拟内容

首先笔者假定读者已经对cache组相联有所了解，如果不了解可以先去看看。

该cache模拟器需要事先指定cache的联合度E(>0),即每个cache块有多少个cache行、cache块的数目S、以及磁盘块(block)的大小B。同时假定一个cache行里的数据大小和磁盘块大小相同。

针对不同的trace文件，在以上设置的cache中，采用LRU替换算法，计算cache的命中次数(hit)，不命中次数(miss)，以及cache块的替换次数(eviction)。

以上就是该cache模拟器所关心的内容，没错，只关心这3个数据，并且采用LRU替换算法，比较简单。



同时该cache模拟器采取写回和写分配策略，即读或写cache不命中时数据都写回cache，并且假设访问数据的地址的字节都对齐，因此可以不考虑trace文件中的size属性列。在这些策略下分析cache应对L,S,M 3种指令的情况。

|                | 命中        | 不命中       | 替换                  |
| -------------- | ----------- | ------------ | --------------------- |
| L (读)         | hit++       | miss++       | miss++,eviction++     |
| S (写)         | hit++       | miss++       | miss++,eviction++     |
| M (修改=读+写) | hit++,hit++ | miss++,hit++ | miss++,eviction,hit++ |

注：因为是写分配策略，如果cache写不命中，内存也要将相应的块写入cache。
最后cache的 **命中次数+不命中次数 = L + S + 2*M**

​	

​	

#### 3. 确定结构

仿造硬件cache的结构，定义cache模拟器的结构。

``` C
typedef struct cache_line {
    char valid;
    mem_addr_t tag;
    unsigned long long lru;
    //忽略实际数据部分
} cache_line_t;	//cache行

typedef cache_line_t* cache_set_t;	//cache块

//cache,实质是一个二重指针，cache[i][j]表示第i+1个cache块的第j+1个cache行
typedef cache_set_t* cache_t;	



miss_count	//miss次数
hit_count	//hit次数
eviction_count	//eviction替换次数
    
E:相联度
S:cache块的数目 (S=2^s, s是S的位数bit)
B:block大小	 (B=2^b，b是B的位数bit)

内存地址的划分
Tag | cache_block_index | block_offset
cache_set_index_mask = 2^s - 1
cache_set_index = (addr >> b) & cache_set_index_mask
tag = addr >> (s+b)
```



这里补充一下LRU算法：假设写入新内容的cache line是cache[i] [j]，那么需要遍历整个cache set(即cache [i]),

对比其中它们每一个lru marker 与 cache[i] [j].lru的大小。如果小于或等于cache[i] [j].lru, 那么该cache line就需要刷新它的lru marker，使其加1，最后注意cache[i] [j]也会刷新lru，其值变回0.





