(module
  (import "env" "eth2_blockDataSize"     (func $eth2_blockDataSize (result i32)))
  (import "env" "eth2_blockDataCopy"     (func $eth2_blockDataCopy (param i32 i32 i32)))
  (import "env" "eth2_savePostStateRoot" (func $eth2_savePostStateRoot (param i32)))
  (import "env" "eth2_getShardId"        (func $eth2_getShardId (result i64)))
  (import "env" "eth2_getShardStateRoot" (func $eth2_getShardStateRoot (param i64 i32)))
  (func $main
    (local i64 i64)

    ;; get shardId
    call $eth2_getShardId
    local.set 0

    ;; get shardid to read the root from, given as calldata
    (call $eth2_blockDataCopy
      (i32.const 32)
      (i32.const 0)
      (call $eth2_blockDataSize))
    (local.set 1
      (i64.load (i32.const 32)))
    

    ;; if this shardid and shardid to get root from nonequal
    local.get 0
    local.get 1
    i64.ne
    if
      ;; read shardid's state root
      local.get 1
      i32.const 32
      call $eth2_getShardStateRoot

      ;; save that shardid's stateroot to our own
      i32.const 32
      call $eth2_savePostStateRoot
    end
  )
    
  (memory 1)
  (export "memory" (memory 0))
  (export "main" (func $main))
)
