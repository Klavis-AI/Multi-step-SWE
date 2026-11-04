begin
  p([[1, 2]].dig(0, 1))
rescue Exception => e
  p e.class
end
