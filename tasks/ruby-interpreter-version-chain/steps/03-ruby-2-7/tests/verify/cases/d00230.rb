begin
  p({a: {b: 1}}.dig(:a, :b))
rescue Exception => e
  p e.class
end
