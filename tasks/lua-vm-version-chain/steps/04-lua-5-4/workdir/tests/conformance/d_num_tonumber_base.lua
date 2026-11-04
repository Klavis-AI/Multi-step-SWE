for _, t in ipairs{{"ff", 16}, {"11", 2}, {"z", 36}, {"Z", 36}, {"10", 8}, {"8", 8},
                   {"-ff", 16}, {"  ff  ", 16}} do
  print(t[1], t[2], tostring(tonumber(t[1], t[2])))
end
print(pcall(tonumber, "10", 1))
print(pcall(tonumber, "10", 37))
