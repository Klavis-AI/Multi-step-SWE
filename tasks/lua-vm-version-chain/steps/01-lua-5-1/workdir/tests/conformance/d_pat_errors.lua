for _, p in ipairs{"(", "[", "%", "%b", "%bx", "%f", "(a", "a)", "%1", "[a-"} do
  local ok, e = pcall(string.find, "abc", p)
  print(p, ok, tostring(e))
end
