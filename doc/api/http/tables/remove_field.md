POST /api/v1/tables/remove_field
================

Remove an existing field from an existing table.

###Resource Information
<table class='http_api'>
  <tr>
    <td>Authentication required?</td>
    <td>Yes</td>
  </tr>
</table>

###Parameters:
<table class='http_api'>
  <tr>
    <td>table</td>
    <td>The name of the table to remove the field from.</td>
  </tr>
  <tr>
    <td>field&#95;name</td>
    <td>The name of the field to remove.</td>
  </tr>
</table>

###Example Request

        >> POST /api/v1/tables/remove_field?table=my_sensor_table&field_name=sensor_location HTTP/1.1
        >> Authorization: Token <authtoken>
        >> Content-Type: text/plain;charset=UTF-8
        >> Content-Length: ...


###Example Response

        << HTTP/1.1 201 Created
        << Content-Length: 0
