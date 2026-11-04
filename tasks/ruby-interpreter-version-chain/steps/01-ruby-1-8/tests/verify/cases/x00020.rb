begin
  p(%w[a b c])
rescue Exception => e
  p e.class
end
