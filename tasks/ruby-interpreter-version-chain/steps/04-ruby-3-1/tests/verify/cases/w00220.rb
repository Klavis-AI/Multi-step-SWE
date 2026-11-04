begin
  p(({a: 1}).each_pair { |k, v| })
rescue Exception => e
  p e.class
end
