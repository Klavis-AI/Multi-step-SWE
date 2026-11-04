begin
  p(({[1] => {c: 2}}).map { |k, v| [k, v] })
rescue Exception => e
  p e.class
end
