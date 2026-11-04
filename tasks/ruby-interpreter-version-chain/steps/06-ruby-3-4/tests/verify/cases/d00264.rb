begin
  p({a: 1}.transform_keys(&:to_s))
rescue Exception => e
  p e.class
end
