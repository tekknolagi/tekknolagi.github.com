---
title: Tufts Faculty Casualties
---

<table>
  <tr><th>Name</th><th>Left in</th></tr>
  {% for person in site.data.tufts_casualties %}
  <tr>
    <td>{{ person.name }}</td><td>{{ person.leave_date }}</td>
  </tr>
  {% endfor %}
</table>
