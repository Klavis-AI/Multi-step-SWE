begin
  p({a: {b: [10, 20]}}.dig(:a, :b, 1))
  p [[1, [2]]].dig(0, 1, 0)
rescue Exception => e
  p e.class
end
