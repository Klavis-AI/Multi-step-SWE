begin
  t = []
  for v in {a: 1}
    t << v
  end
  p t
  p defined?(v)
rescue Exception => e
  p e.class
end
