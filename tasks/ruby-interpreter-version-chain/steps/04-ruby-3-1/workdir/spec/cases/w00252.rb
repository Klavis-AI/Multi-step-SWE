begin
  p(({[1] => {c: 2}}).keys)
rescue Exception => e
  p e.class
end
