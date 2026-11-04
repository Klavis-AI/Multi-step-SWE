begin
  p({a: 1, b: 2}.group_by { |x| 1 })
rescue Exception => e
  p e.class
end
