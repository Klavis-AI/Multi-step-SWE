begin
  p({a: 1}.fetch_values(:a, :b) { |k| k.to_s })
rescue Exception => e
  p e.class
end
