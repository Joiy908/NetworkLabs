cs144/sponge 项目，实现TCP。

先修课程: 数据结构。

> 如果没用过 cmake，强烈建议去学一下，只要了解最基础的概念即可。否则可能会不知道如何处理一些bug。

# setup env

参考官方文档即可: [Setting up your CS144 VM using VirtualBox (stanford.edu)](https://stanford.edu/class/cs144/vm_howto/vm-howto-image.html)

官方给了不同的方式配置环境：用自己的Linux安装各种包，或者用其提供的镜像文件在VirtualBox上运行。

我先用 wsl2 试了一下，没有成功。事实证明 使用镜像文件 是最省事的方案。（虽然需要下载大概1.5G。）

IDE 我用的 Clion，Cmake + Remote Deployment 同步文件和dubug非常方便。 

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



> 写得比较简单。但如果是第一次搭建环境，会遇到许多问题。自己慢慢摸索会学到很多，有疑问google即可。

> 虚拟机内部网速慢的问题：
>
> [VirtualBox 虚拟机里网络很慢的解决方法 - Python List (pylist.com)](https://pylist.com/topic/175.html)
>
> 安装VirtualBox安装目录中。
>
> 执行如下代码：
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

open CmakeList.txt

Ctrl 进入 `etc/test.cmake`,

那里面有 可执行的 test，在源文件下完断点后，就可debug。

## readme

这里只介绍 lab 的**思路**和遇到的 **Bugs**，不放具体代码，推荐自己写。

这个readme，是用来总结我遇到的坑（环境配置和课程理解上的）和解决方案，

并不能代替官方的 `labX.pdf`, 只是作为一个补充进行参考。

只有自己写代码和debug，投入足够的时间，才能真正理解TCP的思想。

# lab0 webget, ByteStream

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



here



