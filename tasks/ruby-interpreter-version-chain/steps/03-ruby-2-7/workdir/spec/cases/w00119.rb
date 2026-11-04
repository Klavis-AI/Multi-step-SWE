begin
  p([[1, 2], [3, 4]].take_while { true })
rescue Exception => e
  p e.class
end
