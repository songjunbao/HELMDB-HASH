## 简介

nvmdb是面向在非易失性内存（non-volatile memory，NVM）上，提供具有高性能、快速故障重启等特点的下一代存储引擎。

## 编译

如果需要将nvmdb的功能编译进openGuass中，需要打开对应的编译宏，同时还有一些依赖条件。

### 依赖

#### 硬件依赖

目前已经完成市场化的NVM，只有Intel的Optane DC PMM系列产品，而且需要配合特定的x86 CPU来使用，无法在arm环境下使用。所以目前nvmdb只支持x86-64平台。  
如果没有pmem的硬件，也可以将nvmdb的目录建立在其它类型的盘符上，例如NVME、内存文件(/dev/shm)系统等，性能上会有差别。  

#### 软件依赖

nvmdb程序依赖于oneTBB（2021.11.0）和pmdk（1.12.0）库，这两个库已编译了现成的二进制文件，放在了三方库中。  
pmdk的程序还依赖与daxctl-libs和daxctl-libs，这两个库可通过对应linux的发型版本的软件管理工具直接安装，一般都有提供，其版本要求为大于等于65。  
当前nvmdb使用了fdw与openGauss进行交互，其中涉及sql引擎的部分复用了一部分mot的代码流程，所以当前还需要打开mot的编译选项。  
（另：pmdk目前已经可以支持ARM平台，但并非正式版本，开发者如果想要在arm上面尝试运行nvmdb，可自行进行下载编译pmdk和tbb，并将nvmdb的编译选项和平台的互斥的编译条件删除，可全局搜索ENABLE_NVMDB或enable-nvmdb）

### make编译

使用make编译openGauss，需要在make_compile.sh文件中加上对应的编译选项--enable-nvmdb，同时需要加上--enable-mot。

### cmake编译

使用cmake编译openGauss，需要在build_opengauss.sh文件中加上编译选项-DENABLE_NVMDB=ON，同时加上-DENABLE_MOT=ON。

## 配置

### guc配置

目前只支持一个可配置的guc参数，nvm_directory，表征着nvmdb数据库文件的存放路径。  
其默认配置为openGauss数据库目录的pg_nvm文件夹下；当对其进行配置时，需要指定一个或多个拥有读写权限的绝对路径，多个路径之间可用英文分号分隔。这里多个目录的作用是为了将数据负载分担到多个设备上，减少可能的IO瓶颈，提升性能。  
例：nvm_directory = '/home/a/data1;/home/a/data2'，将其加入到openGauss的配置文件中。

### 代码中的配置

有些配置目前还没有作为可配置参数，具体可见nvm_cfg.h文件。  
其中较为重要的几个：  
1. NVMDB_MAX_GROUP：支持的最大的路径个数；
2. NVMDB_MAX_THREAD_NUM：支持的最大并发数，可根据需要自行修改；
3. NVMDB_UNDO_SEGMENT_NUM：支持的undo段的数量，其值需大于等于NVMDB_MAX_THREAD_NUM的值，可根据需要自行修改；

## 使用

### 创建nvmdb数据表

创建nvmdb数据表，需要加上FOREIGN的关键字，并且将server指定为nvm_server。  
例：create FOREIGN table nvm_test(x int not null, y float,z VARCHAR(100)) server nvm_server;

### 创建索引

创建索引的方式与普通方式一致。  
例：create index nvm_test_index_x on nvm_test(x);

### 插入
数据插入语句与普通方式一致。  
例：insert into nvm_test (x,y,z) values (1000,3.1415926,'333' );

### 删除
数据删除语句与普通方式一致。  
例：delete from nvm_test where x = 1000;

### 更新
数据更新语句与普通方式一致。  
例：update nvm_test set y = 1.1,z = '111' where x = 1000;

### 查询
查询方式与普通方式一致。  
例：select * from nvm_test where x >= 1000;



