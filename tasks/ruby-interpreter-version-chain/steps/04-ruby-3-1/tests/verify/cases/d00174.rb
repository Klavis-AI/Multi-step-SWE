begin
  p({1 => 2}.index(2))
rescue Exception => e
  p e.class
end
