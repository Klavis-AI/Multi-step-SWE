begin
  t = []
  for v in "abc".each_char.to_a
    t << v
  end
  p t
  p defined?(v)
rescue Exception => e
  p e.class
end
