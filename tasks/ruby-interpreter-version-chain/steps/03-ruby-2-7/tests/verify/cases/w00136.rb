begin
  p({a: 1, b: 2}.flat_map { |x| x })
rescue Exception => e
  p e.class
end
