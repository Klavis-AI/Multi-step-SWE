begin
  p(Comparable.instance_methods.sort.first)
rescue Exception => e
  p e.class
end
