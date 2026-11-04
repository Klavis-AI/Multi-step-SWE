begin
  t = []
  for v in [1, 2, 3]
    t << v
  end
  p t
  p defined?(v)
rescue Exception => e
  p e.class
end
