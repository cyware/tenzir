output "vast_security_group" {
  value = module.vast_server.task_security_group_id
}

output "vast_task_definition" {
  value = module.vast_server.task_definition_arn
}

output "fargate_cluster_name" {
  value = aws_ecs_cluster.fargate_cluster.name
}

output "ids_subnet_id" {
  value = aws_subnet.ids.id
}

output "vast_lambda_name" {
  value = module.vast_client.lambda_name
}
