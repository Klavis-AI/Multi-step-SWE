begin
  p((1..3).overlap?(2..4))
rescue Exception => e
  p e.class
end
