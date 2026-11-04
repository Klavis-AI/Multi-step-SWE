begin
  p({a: 1, b: 2}.take_while { true })
rescue Exception => e
  p e.class
end
