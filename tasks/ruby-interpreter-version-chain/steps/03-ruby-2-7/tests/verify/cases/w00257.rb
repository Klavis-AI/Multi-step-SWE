begin
  p(({[1] => {c: 2}}).sort_by { |k, v| k.to_s })
rescue Exception => e
  p e.class
end
