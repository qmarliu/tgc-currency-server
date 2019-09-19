1. 进入合约文件，编译合约
`eosio-cpp -I include -o deposit.wasm src/deposit.cpp --abigen`

2. 创建合约账户
`cleos create account eosio deposit EVS5nifJ6sHVSai7LzUK4dQdj3W7vTzgVSnKYZJGDr1nbMfpNK1No`

3. 部署合约
`cleos set contract deposit /home/liul/Code/contract/deposit -p deposit@active`

4. 插入memo
`cleos push action deposit insert '{"memo": 1111117}' -p deposit`

5. 设置精度
`cleos push action deposit setprecision '{"minimum_in": 100, "minimum_out": 10000}' -p deposit`

6. 获取数据
根据主键获取数据
`cleos get table deposit deposit balances`
`cleos get table deposit deposit precision`

7. 删除数据
`cleos push action deposit erase '{"memo": 1111117}' -p deposit`
