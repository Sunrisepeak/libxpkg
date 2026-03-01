-- xim.libxpkg.base64: pure-Lua base64 encoder/decoder
local M = {}

local b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local b64lookup = {}
for i = 1, #b64chars do
    b64lookup[b64chars:sub(i, i)] = i - 1
end

function M.encode(data)
    if type(data) ~= "string" then data = tostring(data) end
    local result = {}
    local len = #data
    for i = 1, len, 3 do
        local b1 = data:byte(i)
        local b2 = i + 1 <= len and data:byte(i + 1) or 0
        local b3 = i + 2 <= len and data:byte(i + 2) or 0

        local n = b1 * 65536 + b2 * 256 + b3

        table.insert(result, b64chars:sub(math.floor(n / 262144) % 64 + 1, math.floor(n / 262144) % 64 + 1))
        table.insert(result, b64chars:sub(math.floor(n / 4096) % 64 + 1, math.floor(n / 4096) % 64 + 1))
        if i + 1 <= len then
            table.insert(result, b64chars:sub(math.floor(n / 64) % 64 + 1, math.floor(n / 64) % 64 + 1))
        else
            table.insert(result, "=")
        end
        if i + 2 <= len then
            table.insert(result, b64chars:sub(n % 64 + 1, n % 64 + 1))
        else
            table.insert(result, "=")
        end
    end
    return table.concat(result)
end

function M.decode(data)
    if type(data) ~= "string" then return "" end
    data = data:gsub("[^A-Za-z0-9+/=]", "")
    local result = {}
    for i = 1, #data, 4 do
        local c1 = b64lookup[data:sub(i, i)] or 0
        local c2 = b64lookup[data:sub(i + 1, i + 1)] or 0
        local c3 = b64lookup[data:sub(i + 2, i + 2)]
        local c4 = b64lookup[data:sub(i + 3, i + 3)]

        local n = c1 * 262144 + c2 * 4096
        table.insert(result, string.char(math.floor(n / 65536) % 256))

        if c3 then
            n = n + c3 * 64
            table.insert(result, string.char(math.floor(n / 256) % 256))
        end
        if c4 then
            n = n + c4
            table.insert(result, string.char(n % 256))
        end
    end
    return table.concat(result)
end

return M
