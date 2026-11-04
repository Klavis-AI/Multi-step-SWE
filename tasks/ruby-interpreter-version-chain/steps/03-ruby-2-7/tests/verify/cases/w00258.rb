begin
  p(({[1] => {c: 2}}).min_by { |k, v| k.to_s })
rescue Exception => e
  p e.class
end
