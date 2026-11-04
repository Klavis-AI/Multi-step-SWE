begin
  p 5.yield_self { |x| x + 1 }
  p 5.then { |x| x * 2 }
rescue Exception => e
  p e.class
end
