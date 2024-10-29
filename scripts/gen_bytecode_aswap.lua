function gen_bytecode_aswap()
    pay_contract_creation = function(param)
        -- Unpack parameters with clear comments on their expected types
        print("param length:", #param)
        cbdc_tag, from, to, value, sequence, s_hash, time_exp = string.unpack("c5 c32 c32 I8 I8 c64 I8", param)
        
        -- Get account key using cbdc_tag and name (e.g., public key)
        local function get_account_key(cbdc_tag, name)
            return "account_" .. name .. cbdc_tag
        end

        local function get_swap_key(cbdc_tag, name)
            return "swap_" .. cbdc_tag .. name
        end

        -- Yield the account data from storage
        local function get_account(cbdc_tag, name)
            local account_key = get_account_key(cbdc_tag, name)
            local account_data = coroutine.yield(account_key)

            -- Return the unpacked account data if it exists, otherwise default to balance 0 and sequence 0
            print("account data :", #account_data)
            
            if account_data and #account_data > 0 then
                return string.unpack("I8 I8", account_data) -- balance, seq
            end
            return 0, 0 -- Default return values for non-existent accounts
        end

        -- Create swap payload with proper formatting
        local function swap_payload(from, to, value, time_exp)
            return string.pack("c32 c32 I8 I8", from, to, value, time_exp)
        end

        -- Pack the account data into the updates table
        local function pack_account(updates, cbdc_tag, name, balance, seq)
            print("packing account with updated bal ", balance, " and seq",  seq)
            updates[get_account_key(cbdc_tag, name)] = string.pack("I8 I8", balance, seq)
            print(" account packed successfully")
        end

        -- Pack the swap payload into the updates table
        local function pack_swap(updates, cbdc_tag, s_hash, from, to, value, time_exp)
            local key = get_swap_key(cbdc_tag, s_hash)
            local account_data = coroutine.yield(key)
            local value = swap_payload(from, to, value, time_exp)
            --print("packing swap account  ", key, " and key len", #key ," and value ", value , " and len ", #value)
            updates[key] = value
            print("packed swap account")
 
        end

        -- Consolidated update function to manage both account and swap updates
        local function update(from_acc, from_bal, from_seq, to, s_hash, value, time_exp)
            updates = {}
            pack_account(updates, cbdc_tag, from_acc, from_bal, from_seq)
            pack_swap(updates, cbdc_tag, s_hash, from_acc, to, value, time_exp)
            print("Balance i updated and swap account is placed");
            return updates
        end

        -- Fetch the account details for the "from" account
        local from_balance, from_seq = get_account(cbdc_tag, from)
        print("from_balance :", from_balance)
        print("from_seq :", from_seq)
        
        -- Error handling for sequence and balance checks
        if sequence < from_seq then
            error("Sequence number too low")
        elseif value > from_balance then
            error("Insufficient balance")
        elseif value < 0 then
            error("Value must be greater than zero")
        end

        -- Update balance and sequence number if all checks pass
        from_balance = from_balance - value -- Subtract value from the "from" account balance
        from_seq = sequence + 1 -- Increment the sequence number

        -- Return the updates to be applied (as a table of key-value pairs)
        return update(from, from_balance, from_seq, to, s_hash, value, time_exp)
    end

    pay_contract_execution = function(param)
        print("param length:", #param)
        local cbdc_tag, sk = string.unpack("c5 c64", param)
        --print("cbdc_tag : ", cbdc_tag)
        --print("sk value : ", sk)
        --print("pubk : ", pubk)

        -- Helper function to get account key
        local function get_account_key(cbdc_tag, name)
            return "account_" .. name .. cbdc_tag
        end

        local function get_swap_key(cbdc_tag, name)
            return "swap_" .. cbdc_tag .. name
        end

        -- Fetch swap account data
        local function get_swap_account(cbdc_tag, name)
            local account_key = get_swap_key(cbdc_tag, name)
            local account_data = coroutine.yield(account_key)
            if account_data and #account_data > 0 then
                return account_data
            end
            print("Should not come here")
            return nil
        end

        -- Fetch account details (balance, sequence)
        local function get_account(cbdc_tag, name)
            local account_key = get_account_key(cbdc_tag, name)
            local account_data = coroutine.yield(account_key)
            if account_data and #account_data > 0 then
                return string.unpack("I8 I8", account_data)
            end
            print("Should not come here")
            return nil
        end

        -- Prepare swap payload
        local function swap_payload(from, to, value, time_exp)
            return string.pack("c32 c32 I8 I8", from, to, value, time_exp)
        end

        -- Pack account updates
        local function pack_account(updates, cbdc_tag, name, balance, seq)
            updates[get_account_key(cbdc_tag, name)] = string.pack("I8 I8", balance, seq)
        end

        -- Pack swap updates
        local function pack_swap(updates, cbdc_tag, s_hash, sk)
            updates[get_swap_key(cbdc_tag, s_hash)] = string.pack("c64", sk)
        end

        -- Update account and swap states
        local function update(cbdc_tag, s_hash, acc, bal, seq, sk)
            local ret = {}
            pack_account(ret, cbdc_tag, acc, bal, seq) 
            pack_swap(ret, cbdc_tag, s_hash, sk)
            print("Update Done with pack_accunt and pack Swap ")
            return ret
        end

        local s_hash = hash_string(sk)
        print("Calculates s_hash from sk is : ", s_hash)
        local swap_data = get_swap_account(cbdc_tag, s_hash)
        local current_time = os.time()

        -- Check if swap data exists and process based on expiration
        if swap_data then
            local from, to, value, time_exp = string.unpack("c32 c32 I8 I8", swap_data)
            print("Found Swap Data ")
            -- Refund if time expired
            if current_time > time_exp then
                local bal, seq = get_account(cbdc_tag, from)
                if bal then
                    bal = bal + value
                    seq = seq + 1
                    return update(cbdc_tag, s_hash, from, bal, seq, sk)
                else
                    error("Account not found for refund")
                end

            -- Complete transfer if valid recipient
            else
                local bal, seq = get_account(cbdc_tag, to)
                print("Found correct recipient ", bal, "and seq ", seq)
                if bal then
                    bal = bal + value
                    seq = seq + 1
                    print("new Bal ", bal, "and seq ", seq)
                    return update(cbdc_tag, s_hash, to, bal, seq, sk)
                else
                    error("Account not found for transfer")
                end
            end
        else
            -- Try handling refund txns when swap data is not found
            -- Unique Case when receiver wants to refund his transaction, because
            -- receiver doesnt have sk and Initiator didnt execute the spend transaction 
            local refund_data = get_swap_account(cbdc_tag, sk)
            if refund_data then
                print(" Found Refund  Data ")
                local from, to, value, time_exp = string.unpack("c32 c32 I8 I8", refund_data)
                if current_time > time_exp then
                    local bal, seq = get_account(cbdc_tag, from)
                    if bal then
                        bal = bal + value
                        seq = seq + 1
                        return update(cbdc_tag, s_hash, from, bal, seq, sk)
                    else
                        error("Account not found for refund")
                    end
                else
                    error("Refund time not yet expired")
                end
            else
                error("Invalid secret key")
            end
        end
    end

    get_secret_key = function(param)
        if not param or type(param) ~= "string" then
            error("Invalid input: param must be a non-empty string")
        end
    
        local cbdc_tag, s_hash = string.unpack("c5 c64", param)
        if not cbdc_tag or not s_hash then
            error("Failed to unpack param: invalid format")
        end
    
        -- Helper function to get account key
        local function get_swap_key(cbdc_tag, name)
            return string.format("swap_%s%s", cbdc_tag, name)
        end
    
        -- Fetch swap account data
        local function get_swap_account(cbdc_tag, name)
            local account_key = get_swap_key(cbdc_tag, name)
            local account_data = coroutine.yield(account_key)
            if account_data and #account_data > 0 then
                return account_data
            end
            return nil
        end
    
        local sk = get_swap_account(cbdc_tag, s_hash)
        if not sk then
            error("Input data not present or invalid")
        end
    
        local hash = hash_string(sk)
        if hash ~= s_hash then
            error("Hash mismatch")
        end
    
        return {[get_swap_key(cbdc_tag, s_hash)] = string.pack("c64", sk)}
    end

     -- Create a table to store the bytecodes
    bytecode_table = {}

    -- Function to generate the hex bytecode of a function
    local function get_bytecode(func)
        local bytecode = string.dump(func, true)
        local t = {}
        for i = 1, #bytecode do
            t[#t + 1] = string.format("%02x", string.byte(bytecode, i))
        end
        -- Concatenating the List
        return table.concat(t)
    end

    -- Store the bytecode of each function in the table at different indices
    bytecode_table[0] = get_bytecode(pay_contract_creation)
    bytecode_table[1] = get_bytecode(pay_contract_execution)
    bytecode_table[2] = get_bytecode(get_secret_key)

    return bytecode_table
end
