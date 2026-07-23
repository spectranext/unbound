function cpu_log(text)
{
    var existing = $('#logs').text().split('\n').slice(-10).join("\n");
    if (existing) existing += "\n";
    $('#logs').text(existing + text);
}

$(function() {
    const form = $("form");
    form.on("change", ".file-upload-field", function(){
        const fname = $(this).val().replace(/.*(\/|\\)/, '');
        $(this).parent(".file-upload-wrapper").attr("data-text", fname );
        form.submit(function (ev) {
            ev.preventDefault();

            const formData = new FormData();
            formData.append('snapshot', $('#snapshot')[0].files[0]);

            cpu_log("Uploading " + fname + "...")

            $.ajax({
                type: 'POST',
                url: form.attr('action'),
                data: formData,
                success: function (data) {
                    cpu_log('File ' + fname + ' submitted.');
                    $(".file-upload-wrapper").attr("data-text", "Submitted." );
                },
                error: function (data) {
                    cpu_log('Failed to submit file ' + fname + '.');
                },
                contentType: false,
                processData: false
            });

            form.trigger("reset");
        });

        form.submit();
    });
})
