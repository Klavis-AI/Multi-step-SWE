begin
  p 1 + 1
  p :done
rescue Exception => e
  p e.class
end
