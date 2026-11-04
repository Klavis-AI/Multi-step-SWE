begin
  p(1.then { |x| x + 1 })
rescue Exception => e
  p e.class
end
