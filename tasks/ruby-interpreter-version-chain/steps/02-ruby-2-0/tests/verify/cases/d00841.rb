begin
  [1, 2].map { it }
  p :done
rescue Exception => e
  p e.class
end
