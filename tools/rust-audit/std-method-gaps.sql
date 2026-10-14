-- Canonical receiver heads contain dots; the last segment is the method name.
WITH gaps AS (
  SELECT name, count(*) AS sites
  FROM unresolved_sites
  WHERE kind = 'call' AND reason = 'unrecognised_std_method'
  GROUP BY name
), parts AS (
  SELECT name, sites, '["' || replace(name, '.', '","') || '"]' AS segments
  FROM gaps
)
SELECT substr(name, 1, length(name) - length(j.value) - 1) AS receiver_type,
       j.value AS method, sites
FROM parts JOIN json_each(segments) AS j
WHERE j.key = json_array_length(segments) - 1
ORDER BY sites DESC, receiver_type, method
LIMIT 20;
