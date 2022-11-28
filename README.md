CS144/sponge 项目，实现TCP。

官网: [CS 144: Introduction to Computer Networking](https://cs144.github.io/)

先修课程: 数据结构、C++。

我之前没写过C++，所以学这个课之前去学了 [CS 106L](http://web.stanford.edu/class/cs106l/)。

> 如果没用过 cmake，强烈建议去学一下，只要了解最基础的概念即可。否则可能会不知道如何处理一些bug。

note: 一下所有经验，来源于我做的版本（2022-10左右），官方会不断更新，建议采用最新版。

这个仓库也有我做的这个版本的备份。

# setup env

参考官方文档即可: [Setting up your CS144 VM using VirtualBox (stanford.edu)](https://stanford.edu/class/cs144/vm_howto/vm-howto-image.html)

官方给了不同的方式配置环境：用自己的Linux安装各种包，或者用其提供的镜像文件在VirtualBox上运行。

我先用 wsl2 试了一下，没有成功。事实证明 使用镜像文件 是最省事的方案。（虽然需要下载大概1.5G。）

IDE 我用的 Clion，Cmake + Remote Deployment 同步文件和debug非常方便。 

我的具体做法：

- 下载 virtualbox、镜像文件
- 在 virtualbox 中加载镜像文件
- 配置 Ubuntu 系统：

  - 配置自己的dotFile
  - apt 换源
  - update cmake(可选): [Install Cmake · GitBook (uc3m.es)](https://robots.uc3m.es/installation-guides/install-cmake.html#install-cmake-319-ubuntu-1804-bionic)
  - 配置 github ssh（可选，可以在自己电脑上`git clone`, 再把文件同步到虚拟机）
- 克隆仓库到本地: `git clone git@github.com:CS144/sponge.git`
- 配置 Clion, 主要是设置目录映射和Cmake
  setting-“Build,Execution,Deployment”-Deployment
   设置 ssh连接、目录映射（本地`sponge`, 映射到 `/home/cs144/sponge`）




> 虚拟机内部网速慢的问题：
>
> [VirtualBox 虚拟机里网络很慢的解决方法 - Python List (pylist.com)](https://pylist.com/topic/175.html)
>
> 在安装VirtualBox安装目录中执行如下代码：
>
> ```bash
> ./VBoxManage.exe modifyvm cs144_vm --natdnshostresolver1 on
> ./VBoxManage.exe modifyvm cs144_vm --natdnsproxy1 on
> ```
>
> 网速就和宿主机一样了。

## build

```bash
ssh -p 2222 cs144@localhost
cd sponge

mkdir build && cd build && cmake ..
# or cmake -B build
# 如果用的Clion没必要用这一步, Clion 会自动创建可执行目录,
# like: cmake-build-debug-remote-host

make -j4 #-j4 表示用 4个 processors make

# 写完代码后 test, 以lab0为例
make check_webget
make check_lab0
```

## debug

如果 `make check_labx` 没通过：

进入 `sponge/tests` 中，找到对应的测试文件，然后用 Clion debug。

可能有部分 test.cc 显示 `This file does not belong to any project;...`, 如 `tests/fsm_retx.cc`。

Solution: 去 `/tests/CmakeList.cmake` 添加 `add_test_exec (fsm_retx)` 即可。

（这就是为什么要学一点 Cmake）

在源文件下完断点后，就可debug。

## 实验总体结构

<img src=".images/20221107124904.jpg" alt="demo" style="zoom:80%;" />

从 Lab1 到 Lab4，我们将会实现 TCP, 也就是 `TCPSocket`。如上图所示，`TCPSocket` 内部操作封装的 `TCPConnection`, 后者有操纵 `TCPSender` 和 `TCPReceiver`, 他们内部用到 `ByteStream` 这个数据结构。`TCPReceiver` 内部比 `TCPSender` 多了一个 `StreamReassembler` 用来缓存“后发先至”的 segment。

实任务是去补全这些类的核心方法。顺序是： `lab0:ByteStream` => `lab1:Reaasembler` => `lab2:Receiver` => `lab3:Sender` => `lab4:Connection`。

可以看到，是自底向上的。这样安排的好处是：在实现底层方法的时候，不用考虑其他层的事情，做好眼前的事即可。但也有其弊端：我们如果能知道上层会在何种场景调用，会对该方法有很好的理解，实现的思路更清晰。

事实是，我在实现某些方法时，并不能从`labx.pdf` 的描述中完全 get 到某个方法的作用是什么。比如，在实现`Sender::fill_window()`前，我就有一些困惑：fill_widow() 是 fill 谁的 window？生成的 segment 需要发出去吗？别的方法需要调用`fill_window()` 吗？需要允许重复发送吗？

等到我实现 `TCPConnection`(`Sender` 的调用者) 时，我就完全清楚上面问题的答案了。但是，显然我们直接跳去 `lab4` 去理解这些也不太好。好在，CS144 为我们提供了**非常丰富**测试代码，考虑到了各种 corner case，这些测试可以帮我们完善我们的代码。

不过，这也是建立在自己已经写了大部分代码的情况下，如果一点思路都没有，完全没有 get 到某个方法的idea怎么办呢？：你可以来这里看看，这个短文会给出我自己在写代码前的一些困惑和解答、还有遇到的一些微妙的bug、和一些完全没有必要遇到的TCP思想之外的问题。

在解决这些问题的过程中，可以学到很多（比如Lab0的一些bug，逼我去学了 cmake）。但我也不得不承认，有些痛苦完全没必要经历一遍。所以我写下这个，希望可以给后来的同学解决一些坑，让其专注于TCP本身的复杂度，减少在lab之外（如搭建环境）所花费的时间（我认为自己debug花的时间有点多）。

本文并不能代替官方的 `labX.pdf`, 可以作为一个补充进行参考。自己解决的问题越多，对代码的理解也越多，成就感越强，所以，建议实在想不清楚时再看其他人的思路。

我在完成lab的过程中，也参考了别人的博客，但代码都是自己写的（所以一开始有些丑陋:）。链接如下：

- [doraemonzzz](http://doraemonzzz.com/tags/CS144/)

- [Misaka's blog (misaka-9982.com)](https://www.misaka-9982.com/)
- [CS自学指南 (csdiy.wiki)](https://csdiy.wiki/)
- 参考链接的链接:-x

# Lab0 webget, ByteStream

[lab0.pdf (cs144.github.io)](https://cs144.github.io/assignments/lab0.pdf)

## 1 webget

**目标:** 调用系统 TCP库，写一个 wget like program. 这是一个`application` 层的应用。

**思路:** 

TCP 的 interface 叫 Socket。(Python 和 Java 中都有对应的包)

先去读`TCPSocket` 的文档，然后去实现 `sponge/apps/webget.c` 的 `get_URL()` 方法。

大概10行左右。

**测试:**

```bash
cd build
make -j4 && make check_webge
```



**可能遇到的Bug**:

check_webget 可能报错：

- 如果是`BAD_COMMAND`, 是由于 win => linux 文件格式 或者 `.sh` 没有可执行权限:

  ```bash
  vim tests/webget_t.sh
  :set ff=unix
  ZZ
  
  # 加上x权限
  chmod +x tests/webget_t.sh
  ```

- 网速问题：虚拟机内部网速比宿主机慢。-> 见上面 setup env。

## 2 ByteStream

这个没有太多要注意的，仔细读官方提供的材料，选择一个合适的数据结构即可。

因为C++ 没有 `byte` type, 用 `char` 表示。

`eof` 和 `input_ended` 是两种状态。

# Lab1 Reaasembler

> 注意: 课程并不是一开始提供整个代码框架，而是一点一点暴露。
>
> 切换到Lab2，应该用 `git merge`.

Reaasembler 是 Receiver 的一部分，用来缓存哪些后发先至的 segment。

在这里实现后，写Receiver时任务就少一些。

 

这张图很重要:
<img src=".images/20221107114515.png" alt="demo" style="zoom: 50%;" />



当 Receiver 收到 segment 之后，会传给 `Reassembler`, 由其进行顺序整理，讲有序 data 放入 `ByteStream`（上图中绿色的部分），顺序错乱的 data 放入 `Reassembler` 的 buffer 中保存。（红色的部分）。

如果 data 的 index，是已经在 `ByteStream` 中的(上图蓝的部分) 或 其 index 超过了 `Reassembler` 的容量（上图中 `first unacceptable` 之后的），那 `Reassembler` 就把他们丢弃。



`StreamReassembler`的核心方法是`push_substring(const string &data, const size_t index, const bool eof)`.

它的主要任务是：

- 根据 index，把`data`, 放入上图中的合适位置（可能丢弃）。

这也不太复杂，难点是 要假设：这些不能保证顺序的 data 可能会 互相重叠！这是我在 *Computer Networking A Top-Down Approach* 中，没有读到的。一开始我感到很奇怪，为什么 data 会重叠呢？

其实这个问题在 `rassembler` 中就体现了，如果对方发来的信息由于我们的 `Receiver` 容量不够，我们接收了一些，丢弃了一些。然后回复对应的 `ack number`；等对方的 `sender` 重发时，可能会带着一部分我们已经 `ack` 的 data。 或者，由于 我们 `receive window` 发生变化，所以对方可能重发的时候用不同的 `window size`，导致了重叠。

所以，我们应该监测并处理 overlap。

如何处理呢？如果你有自己的想法，请去实践吧。如果没有，下面是我的做法：

`if the data overlaps with old one, CUT IT!` 把重复的部分切掉。

这就要枚举出 所有可能的情况并处理。 [Misaka](https://www.misaka-9982.com/) 画的图很清楚：[CS144-Lab1: stitching substrings into a byte stream | Misaka's blog (misaka-9982.com)](https://www.misaka-9982.com/2022/01/25/CS144-Lab1/)



然后就是课程文档中说

> *How should inconsistent substrings be handled? You may assume that they don’t exist. That is, you can assume that there is a unique underlying byte-stream, and all substrings are (accurate) slices of it.*

我的理解是，会有 overlap，但overlap的数据都一样，不会前后不一致。

但测试中出现了前后不一致的情况。要采用新的 overlap 的部分才能通过测试。

# Lab2 Receiver

前面的 `reassembler` 处理 Receiver 的好多工作，所以这个 lab 的代码量就没多少了。



here

