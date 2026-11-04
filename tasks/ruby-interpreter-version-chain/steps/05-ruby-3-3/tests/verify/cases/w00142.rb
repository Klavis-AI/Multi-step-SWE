begin
  p({a: 1, b: 2}.partition { |x| true })
rescue Exception => e
  p e.class
end
