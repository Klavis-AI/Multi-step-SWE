begin
  f = ->(x) do x * 2 end
  p f.call(2)
rescue Exception => e
  p e.class
end
