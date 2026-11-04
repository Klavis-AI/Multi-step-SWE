SELECT box '((0,0),(3,3))' @> box '((0,0),(2,2))';
SELECT box '((0,0),(2,2))' <@ box '((0,0),(3,3))';
SELECT point '(1,1)' <@ box '((0,0),(3,3))';
SELECT circle '((0,0),3)' @> circle '((0,0),2)';
