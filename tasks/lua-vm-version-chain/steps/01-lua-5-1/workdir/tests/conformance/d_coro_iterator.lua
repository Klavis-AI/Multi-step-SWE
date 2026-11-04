local function iter(t)
  return coroutine.wrap(function()
    for i, v in ipairs(t) do coroutine.yield(i, v) end
  end)
end
for i, v in iter{"a", "b"} do print(i, v) end
local co = coroutine.create(function() return coroutine.running() end)
local ok, r = coroutine.resume(co)
print(ok, r == co)
print(tostring(coroutine.isyieldable and coroutine.isyieldable()))
print(tostring(coroutine.close and "has-close"))
