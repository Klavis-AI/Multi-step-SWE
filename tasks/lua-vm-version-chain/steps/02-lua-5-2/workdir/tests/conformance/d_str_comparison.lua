print("abc" < "abd", "abc" < "abcd", "" < "\0", "\0" < "\1")
print(("a"):rep(3) == "aaa")
local t = {"b", "a", "C", "B"}
table.sort(t)
print(table.concat(t, ","))
