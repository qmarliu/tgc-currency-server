
本文档将介绍使用图灵开发的currency_plugin插件来适配TGC的充提币业务

# 说在前面
1. TGC链是修改EOSIO源码而来，如果您已经实现了EOS链上的充提币业务，则完成可以用您自己的EOS充提币代码来实现TGC的充提币业务，逻辑是一样的。（如果您想使用我们的代码框架来实现币服务器，请仔细阅读本文档）
2. 要修改这份币服务器代码，需要您对EOSIO代码有一定了解，至少搭建过EOSIO的链，并能正确解读EOS区块中的数据结构。
3. 本充提币服务器有2部分代码
+ Node结点币服务器代码，用来解析TGC链上的交易，监测是否有用户转账到交易所，或是否有交易所账户转出
+ 提现代码，提供后台给用户提现的接口，这个代码需要管理一个账户，用户提现金额会从这个账户转出。

# 下载代码
1. node结点币服务器代码
    git clone https://github.com/qmarliu/tgc-currency-server
    这个代码是基于主网1.7.1版本，只添加了currency_plugin插件。如果您想使用其它eos版本，只需要将里面的currency_plugin插件移植过去即可。
2. 提现代码
    git clone https://github.com/qmarliu/currency_server_helper

# 修改currency_plugin插件适配自己的交易所
当检测到有用户充值或提币时，需要告诉交易所后台或服务器充值到账或提币成功。因为各个交易所服务器提供的接口并不相同，所以需要修改currency_plugin.cpp中的充值和提币请求函数。

## 充值请求函数：
当插件检测到有人充值时，会调用send_deposit_request函数。
send_deposit_request(const currency_plugin_impl::deposit_data &d, int type);
参数说明：

参数1: d. 检测到的充值相关数据信息
```
struct deposit_data {
    std::string user_id; //充值交易所用户ID（从转账的memo中提取出来的）
    std::string symbol;  //充值的币种
    std::string balance; //充值的金额
    std::string from;    //充值的TGC链上账户
    uint32_t height;     //充值的高度
    std::string txid;    //充值记录所有的交易ID
    uint64_t business_id;//控制字段。（可以不用，也可以用时间戳来表示这个充值在交易所的唯一性等用途。）
};
```

参数2: type 充值类型（1表示收到充值交易，但并不表示完全到账， 2表示经过一次网络确认，币已经转账成功）

函数使用的是libcurl库来调用远程接口，可根据远程接口的不同，修改相应的代码和参数
提币请求调用：

当插件检测到有人提币时，会调用send_withdraw_request函数。
send_withdraw_request(const currency_plugin_impl::withdraw_data &d, int type)
参数说明：

参数1: d. 检测到的提现相关数据信息
```
struct withdraw_data {
    std::string user_id; //提现交易所用户ID（从转账的memo中提取出来的）
    std::string symbol;  //提现的币种
    std::string balance; //提现的金额
    std::string to;      //提现的TGC链上账户
    uint32_t height;     //提现的高度
    std::string txid;    //提现记录所有的交易ID
    uint64_t business_id;//控制字段。（可以不用，也可以用时间戳来表示这个充值在交易所的唯一性等用途。）
};
```

参数2: type 提现（1表示收到提现交易，但并不表示完全到账， 2表示经过一次网络确认，币已经转账成功）
函数使用的是libcurl库来调用远程接口，可根据远程接口的不同，修改相应的代码和参数

# 正常启动流程
## 启动node结点币服务
这个结点的启动，和EOS普通结点的启动是一样的。但要添加如下配置
    # 检测充值和提现的插件
    plugin = eosio::currency_plugin

    # 下面是currency_plugin中的配置选项
    # 充提币的币种，可配置1到多个
    currency-symbol = TGC
    # 发币的合约账户，如果币种currency-symbol配置多个，则这个配置也需要配置相同个数
    token-contract = evsio.token

    # 充币账户，用户向交易所的充值，都是充到这个地址中, 程序会检测这个账户是否有币转入。
    currency-deposit-user = deposit

    # 提币账户，用户提现，都是从这个账户取出，程序会检测这个账户是否有币转出
    currency-withdraw-user = extract

    # 当结点检测到有用户充值或提币时，会执行一个远程调用来通知服务器或后台。可根据需要填写地址
    currency-curl-url = http://127.0.0.1:8080

    # p2p网络配置
    # TODO 

## 启动提现代码currency_server_helper程序
这个程序会提供提币的接口，并调用cleos与node结点和keosd钱包交互。
### 编译（需要libev和jansson库的支持）
1. 编译网络库, 进入network，执行make 
2. 编译基础设施库， 进入utils目录，执行make
3. 编译currency_server_helper程序, 进入currency_server_helper目录，执行make

编译后，会在currency_server_helper目录生成currency_server_helper.exe可执行文件
### 配置文件
```
{
    "debug": true,
    "process": {
        "file_limit": 1000000,
        "core_limit": 1000000000
    },
    "log": {
        "path": "/var/log/trade/currency_server_helper",
        "flag": "fatal,error,warn,info,debug,trace",
        "num": 10
    },
    "svr": {    //程序监听的端口，通过通过这个端口来接收http的调用
        "bind": [
            "tcp@0.0.0.0:8082"
        ],
        "max_pkg_size": 102400
    },
    "worker_num": 1, //工作线程数量
    "excutor": "/home/liul/Code/evs/build/programs/cleos/cleos", //cleos的位置和钱包的位置
    "node": "http://evo-chain.kkg222.com",   //node结点的http接口
    "wallet_passwd": "PW5KRu92ZLqjCG4L4wNNKkp48jPzRSiwo8fepYts6njKXo4LyJqC8", //钱包密码
    "funds_user": "deposit",  //提现转出的账户
    "contract_user": "deposit"   //合约账户，可以写一个合约，来限制用户的转入和转出。（例如只有填写指定memo的用户才能充值成功，充值的最小金额等。）
}
```