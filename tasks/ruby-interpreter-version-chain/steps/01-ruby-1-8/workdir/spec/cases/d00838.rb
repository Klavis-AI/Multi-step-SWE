begin
  s = "abc"; s << "d"
  p :done
rescue Exception => e
  p e.class
end
