begin
  p((1..4).filter_map { |x| x })
rescue Exception => e
  p e.class
end
