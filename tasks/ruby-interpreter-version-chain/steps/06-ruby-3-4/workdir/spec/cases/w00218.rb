begin
  p(({a: 1}).sort_by { |k, v| k.to_s })
rescue Exception => e
  p e.class
end
